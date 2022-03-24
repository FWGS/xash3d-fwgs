./waf build -T debug
pushd ./build-debug
./waf install 
./xash3d -dev 2 -ref vk
popd
