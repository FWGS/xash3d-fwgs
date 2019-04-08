#!/bin/bash

. scripts/lib.sh

# Build engine

cd $TRAVIS_BUILD_DIR
python waf configure -s "$HOME/Library/Frameworks/SDL2.framework/" -T debug --prefix="pkg/" --win-style-install || die
python waf build || die
python waf install || die
cp ~/Library/Frameworks/SDL2.framework/SDL2 pkg/libSDL2.dylib
tar -cjf $TRAVIS_BUILD_DIR/xash3d-osx.tar.bz2 pkg/*
