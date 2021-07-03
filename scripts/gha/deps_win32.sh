#!/bin/bash

curl http://libsdl.org/release/SDL2-devel-$SDL_VERSION-VC.zip -o SDL2.zip
unzip -q SDL2.zip
mv SDL2-$SDL_VERSION SDL2_VC
