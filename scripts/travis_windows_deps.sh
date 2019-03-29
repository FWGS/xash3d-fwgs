# python 3.6
choco install python3

# SDL2 for VC prebuilt
curl http://libsdl.org/release/SDL2-devel-$SDL_VERSION-VC.zip -o SDL2.zip
unzip SDL2.zip
mv SDL2-$SDL_VERSION SDL2_VC

cd $TRAVIS_BUILD_DIR
