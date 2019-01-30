# brew install python
curl -s https://www.libsdl.org/release/SDL2-$SDL_VERSION.dmg > SDL2.dmg
hdiutil attach SDL2.dmg
cd /Volumes/SDL2
mkdir -p ~/Library/Frameworks
cp -r SDL2.framework ~/Library/Frameworks/
