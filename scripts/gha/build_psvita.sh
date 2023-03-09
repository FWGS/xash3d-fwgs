#!/bin/bash

. scripts/lib.sh

build_hlsdk()
{
	echo "Building HLSDK: $1 branch..."
	git checkout $1
	./waf configure -T release --psvita || die_configure
	./waf build install --destdir=../pkgtemp/data/xash3d || die
	./waf clean
}

export VITASDK=/usr/local/vitasdk
export PATH=$VITASDK/bin:$PATH

cd "$BUILDDIR" || die

rm -rf artifacts build pkgtemp

mkdir -p pkgtemp/data/xash3d/{valve,gearbox,bshift}/{dlls,cl_dlls} || die
mkdir -p artifacts/ || die

echo "Building vitaGL..."

make -C vitaGL NO_TEX_COMBINER=1 HAVE_UNFLIPPED_FBOS=1 HAVE_PTHREAD=1 SINGLE_THREADED_GC=1 MATH_SPEEDHACK=1 DRAW_SPEEDHACK=1 HAVE_CUSTOM_HEAP=1 -j2 install || die

echo "Building vrtld..."

pushd vita-rtld
cmake -S. -Bbuild -DCMAKE_BUILD_TYPE=Release || die_configure
cmake --build build -- -j2 || die
cmake --install build || die
popd

echo "Building SDL..."

pushd SDL
cmake -S. -Bbuild -DCMAKE_TOOLCHAIN_FILE=${VITASDK}/share/vita.toolchain.cmake -DCMAKE_BUILD_TYPE=Release -DVIDEO_VITA_VGL=ON || die_configure
cmake --build build -- -j2 || die
cmake --install build || die
popd

echo "Building engine..."

./waf configure -T release --psvita || die_configure
./waf build install --destdir=pkgtemp/data/xash3d || die
cp build/engine/xash.vpk pkgtemp/

echo "Building HLSDK..."

# TODO: replace with hlsdk-portable and the correct branch names when PRs are merged
pushd hlsdk-xash3d
build_hlsdk vita-mobile_hacks valve
build_hlsdk vita-opfor gearbox
build_hlsdk vita-bshift bshift
popd

echo "Generating default config files..."

pushd pkgtemp/data/xash3d/valve

touch config.cfg
echo 'unbindall'                    >> config.cfg
echo 'bind A_BUTTON "+use"'         >> config.cfg
echo 'bind B_BUTTON "+jump"'        >> config.cfg
echo 'bind X_BUTTON "+reload"'      >> config.cfg
echo 'bind Y_BUTTON "+duck"'        >> config.cfg
echo 'bind L1_BUTTON "+attack2"'    >> config.cfg
echo 'bind R1_BUTTON "+attack"'     >> config.cfg
echo 'bind START "escape"'          >> config.cfg
echo 'bind DPAD_UP "lastinv"'       >> config.cfg
echo 'bind DPAD_DOWN "impulse 100"' >> config.cfg
echo 'bind DPAD_LEFT "invprev"'     >> config.cfg
echo 'bind DPAD_RIGHT "invnext"'    >> config.cfg
echo 'gl_vsync "1"'                 >> config.cfg
echo 'sv_autosave "0"'              >> config.cfg
echo 'voice_enable "0"'             >> config.cfg

touch video.cfg
echo 'fullscreen "1"' >> video.cfg
echo 'width "960"'    >> video.cfg
echo 'height "544"'   >> video.cfg
echo 'r_refdll "gl"'  >> video.cfg

touch opengl.cfg
echo 'gl_nosort "1"'  >> opengl.cfg

cp *.cfg ../gearbox/
cp *.cfg ../bshift/

popd

echo "Packaging artifacts..."

pushd pkgtemp
7z a -t7z ../artifacts/xash3d-fwgs-psvita.7z -m0=lzma2 -mx=9 -mfb=64 -md=32m -ms=on -r xash.vpk data/
popd
