#!/bin/sh
SCRIPTDIR=$(dirname "$0")
TOPDIR=$SCRIPTDIR/../../
cp $SCRIPTDIR/Makefile.dllloader $TOPDIR/loader/
cp $SCRIPTDIR/Makefile.emscripten $TOPDIR/engine/
cp $SCRIPTDIR/Makefile.linux $TOPDIR/engine/
cp $SCRIPTDIR/wscript $TOPDIR/engine/