sudo mkdir -p /opt/toolchains
cd /opt/toolchains/
sudo git clone https://github.com/a1batross/motomagx_toolchain motomagx
cd $TRAVIS_BUILD_DIR

git clone https://github.com/FWGS/hlsdk-xash3d hlsdk
