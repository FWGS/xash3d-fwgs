sudo mkdir -p /opt/toolchains
cd /opt/toolchains/
sudo git clone https://github.com/a1batross/motomagx_toolchain motomagx
cd $TRAVIS_BUILD_DIR

git clone https://github.com/FWGS/hlsdk-xash3d hlsdk -b mobile_hacks --depth=1
git clone https://github.com/mittorn/ref_soft ref_soft --depth=1

sed -i "s|#rsw||" wscript
