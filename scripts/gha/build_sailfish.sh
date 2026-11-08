#!/bin/bash

. scripts/lib.sh

cd "$BUILDDIR" || die

mkdir -p artifacts/ || die

echo "Building RPM with Sailfish Platform SDK..."

docker exec -u mersdk sailfish mb2 -t "SailfishOS-$SAILFISH_TARGET_VERSION-$GH_CPU_ARCH" --specfile sailfish/harbour-xash3d-fwgs.spec build -j"$(nproc)" || die

cp -v RPMS/harbour-xash3d-fwgs-[0-9]*."$GH_CPU_ARCH".rpm artifacts/harbour-xash3d-fwgs."$GH_CPU_ARCH".rpm || die
