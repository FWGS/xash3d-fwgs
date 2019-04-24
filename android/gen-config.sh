#!/bin/bash

git update-index --assume-unchanged $XASH_CONFIG
PKG_TEST=false
CHECK_SIGNATURES=false
RELEASE=false
GP_VERSION=false
XASH_CONFIG=$1src/in/celest/xash3d/XashConfig.java
shift
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
echo package in.celest.xash3d\; >$XASH_CONFIG
echo public class XashConfig { >>$XASH_CONFIG
echo $_V PKG_TEST = $PKG_TEST\; >>$XASH_CONFIG
echo $_V CHECK_SIGNATURES = $CHECK_SIGNATURES\; >>$XASH_CONFIG
echo $_V RELEASE = $RELEASE\; >>$XASH_CONFIG
echo $_V GP_VERSION = $GP_VERSION\; >>$XASH_CONFIG
echo } >>$XASH_CONFIG
cat $XASH_CONFIG
