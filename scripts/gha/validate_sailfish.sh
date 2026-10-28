#!/bin/bash

. scripts/lib.sh

cd "$BUILDDIR" || die

echo "Downloading Harbour RPM validator..."

git clone --depth 1 https://github.com/sailfishos/sdk-harbour-rpmvalidator build/sdk-harbour-rpmvalidator || die

echo "Validating RPM..."

# exits with non-zero only on errors, warnings (e.g. unstripped binaries) don't fail it
docker exec -u mersdk sailfish bash build/sdk-harbour-rpmvalidator/rpmvalidation.sh artifacts/harbour-xash3d-fwgs.*.rpm || die
