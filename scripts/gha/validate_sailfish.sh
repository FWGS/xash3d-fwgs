#!/bin/bash

. scripts/lib.sh

cd "$BUILDDIR" || die

echo "Downloading Harbour RPM validator..."

git clone --depth 1 https://github.com/sailfishos/sdk-harbour-rpmvalidator build/sdk-harbour-rpmvalidator || die

echo "Validating RPM..."

# exits with non-zero only on errors, warnings (e.g. unstripped binaries) don't fail it
# validate the RPM as built, the artifact copy is renamed and fails the file name check
docker exec -u mersdk sailfish bash build/sdk-harbour-rpmvalidator/rpmvalidation.sh RPMS/harbour-xash3d-fwgs-[0-9]*."$GH_CPU_ARCH".rpm || die
