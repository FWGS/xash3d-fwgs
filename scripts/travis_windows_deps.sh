# python 2.7 cp65001 support
python -m pip install -U pip
pip install win-unicode-console

# SDL2 for VC prebuilt
curl http://libsdl.org/release/SDL2-devel-$SDL_VERSION-VC.zip -o SDL2.zip
unzip SDL2.zip
mv SDL2-$SDL_VERSION SDL2_VC

cd $TRAVIS_BUILD_DIR
