#!/bin/bash

git update-index --assume-unchanged src/in/celest/xash3d/XashConfig.java
PKG_TEST=false
if [[ $* =~ "test" ]] ; then  export PKG_TEST=true; fi

CHECK_SIGNATURES=false
if [[ $* =~ "sign" ]] ; then  export CHECK_SIGNATURES=true; fi

RELEASE=false
if [[ $* =~ "release" ]] ; then  export RELEASE=true; fi

GP_VERSION=false
if [[ $* =~ "gp" ]] ; then  export GP_VERSION=true; fi

_V="public static final boolean"
echo package in.celest.xash3d\; >src/in/celest/xash3d/XashConfig.java
echo public class XashConfig { >>src/in/celest/xash3d/XashConfig.java
echo $_V PKG_TEST = $PKG_TEST\; >>src/in/celest/xash3d/XashConfig.java
echo $_V CHECK_SIGNATURES = $CHECK_SIGNATURES\; >>src/in/celest/xash3d/XashConfig.java
echo $_V RELEASE = $RELEASE\; >>src/in/celest/xash3d/XashConfig.java
echo $_V GP_VERSION = $GP_VERSION\; >>src/in/celest/xash3d/XashConfig.java
echo } >>src/in/celest/xash3d/XashConfig.java
cat src/in/celest/xash3d/XashConfig.java


