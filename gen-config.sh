#!/bin/bash

git update-index --assume-unchanged src/in/celest/xash3d/XashConfig.java
PKG_TEST=false
CHECK_SIGNATURES=false
RELEASE=false
GP_VERSION=false
case "$*" in
*test*) export PKG_TEST=true;;
esac
case "$*" in
*sign*) export CHECK_SIGNATURES=true;
esac
case "$*" in
*release*) export RELEASE=true;;
esac
case "$*" in
*gp*) export GP_VERSION=true;;
esac
_V="public static final boolean"
echo package in.celest.xash3d\; >src/in/celest/xash3d/XashConfig.java
echo public class XashConfig { >>src/in/celest/xash3d/XashConfig.java
echo $_V PKG_TEST = $PKG_TEST\; >>src/in/celest/xash3d/XashConfig.java
echo $_V CHECK_SIGNATURES = $CHECK_SIGNATURES\; >>src/in/celest/xash3d/XashConfig.java
echo $_V RELEASE = $RELEASE\; >>src/in/celest/xash3d/XashConfig.java
echo $_V GP_VERSION = $GP_VERSION\; >>src/in/celest/xash3d/XashConfig.java
echo } >>src/in/celest/xash3d/XashConfig.java
cat src/in/celest/xash3d/XashConfig.java


