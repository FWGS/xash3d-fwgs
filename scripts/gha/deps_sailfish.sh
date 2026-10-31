#!/bin/bash

. scripts/lib.sh

cd "$BUILDDIR" || die

SDK_URL=https://releases.sailfishos.org/sdk
CHROOT=Sailfish_OS-$SAILFISH_TARGET_VERSION-Platform_SDK_Chroot-i486.tar.bz2
TOOLING=Sailfish_OS-$SAILFISH_TARGET_VERSION-Sailfish_SDK_Tooling-i486.tar.7z
TARGET=Sailfish_OS-$SAILFISH_TARGET_VERSION-Sailfish_SDK_Target-$GH_CPU_ARCH.tar.7z
IMAGES_DIR=$RUNNER_TEMP/sailfish-sdk

fetch_sdk_image()
{
	local file
	file=$(basename "$1")

	wget -q "$2" -O "$file.md5" || die

	# images are restored by actions/cache when possible
	if [ "$(md5sum "$file" 2> /dev/null | cut -d' ' -f1)" != "$(cut -d' ' -f1 "$file.md5")" ]; then
		wget -q "$1" -O "$file" || die
		[ "$(md5sum "$file" | cut -d' ' -f1)" == "$(cut -d' ' -f1 "$file.md5")" ] || die
	fi
}

echo "Downloading hlsdk-portable..."

# mb2 builds in the source tree without running %prep, the spec expects it there
git clone --depth 1 --recursive https://github.com/FWGS/hlsdk-portable -b mobile_hacks hlsdk-portable || die

echo "Downloading Sailfish Platform SDK..."

mkdir -p "$IMAGES_DIR" || die
pushd "$IMAGES_DIR" || die
fetch_sdk_image "$SDK_URL/installers/$SAILFISH_SDK_VERSION/$CHROOT" "$SDK_URL/installers/$SAILFISH_SDK_VERSION/$CHROOT.md5"
fetch_sdk_image "$SDK_URL/targets/$TOOLING" "$SDK_URL/targets/$TOOLING.md5sum"
fetch_sdk_image "$SDK_URL/targets/$TARGET" "$SDK_URL/targets/$TARGET.md5sum"
popd || die

echo "Starting Platform SDK container..."

docker import --platform linux/386 "$IMAGES_DIR/$CHROOT" sailfish-sdk || die
# the container stays up for the following steps, they run in it with docker exec
docker run -d --name sailfish --platform linux/386 -v "$PWD:$PWD" -v "$IMAGES_DIR:$IMAGES_DIR" -w "$PWD" sailfish-sdk sleep infinity || die

# SDK tools refuse to run as root, set up the user like sdk-chroot does
docker exec sailfish useradd -m -u "$(id -u)" -s /bin/bash mersdk || die
docker exec sailfish sh -c 'echo "mersdk ALL=NOPASSWD: ALL" > /etc/sudoers.d/sdk && chmod 0440 /etc/sudoers.d/sdk' || die

echo "Installing SDK tooling and target..."

docker exec -u mersdk sailfish sdk-assistant -y create "SailfishOS-$SAILFISH_TARGET_VERSION" "$IMAGES_DIR/$TOOLING" || die
docker exec -u mersdk sailfish sdk-assistant -y create "SailfishOS-$SAILFISH_TARGET_VERSION-$GH_CPU_ARCH" "$IMAGES_DIR/$TARGET" || die
